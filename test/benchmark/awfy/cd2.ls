// AWFY Benchmark: CD (Collision Detection) — Typed version
// COW-safe port with native scalar annotations for MIR Direct

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

type Arr = {l0: array, sz: int}

// =====================================================
// Helpers
// =====================================================
pn safe_div(a: float, b: float) float {
    if (b == 0) { return 0.0 }
    var r = a / b
    return r
}

pn min_f(a: float, b: float) float {
    if (a <= b) { return a }
    return b
}

pn max_f(a: float, b: float) float {
    if (a >= b) { return a }
    return b
}

pn check_overlap(low: float, high: float) int {
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

// =====================================================
// 3-level indexed array: 16 x 16 x 32 = 8192 cap
// =====================================================
pn arr_new() Arr {
    let init: array = fill(16, null)
    var a = { l0: init, sz: 0 }
    return a
}

pn arr_get(a: Arr, idx: int) any {
    var i2 = idx % 32
    var mid = shr(idx, 5)
    var i1 = mid % 16
    var i0 = shr(mid, 4)
    var l0 = a.l0
    var c1 = l0[i0]
    if (c1 == null) { return null }
    var c2 = c1[i1]
    if (c2 == null) { return null }
    var r = c2[i2]
    return r
}

pn arr_set(var a: Arr, idx: int, val) int {
    var i2 = idx % 32
    var mid = shr(idx, 5)
    var i1 = mid % 16
    var i0 = shr(mid, 4)
    var l0 = (a.l0)
    var c1 = l0[i0]
    if (c1 == null) {
        var _d = 0
        c1 = fill(16, null)
    }
    var c2 = c1[i1]
    if (c2 == null) {
        var _d2 = 0
        c2 = fill(32, null)
    }
    var _d3 = 0
    c2[i2] = val
    c1[i1] = c2
    l0[i0] = c1
    a.l0 = l0
    return 0
}

// =====================================================
// Small vector: 16x16=256
// =====================================================
pn vec_new() array {
    return []
}

pn vec_add(var v, item) int {
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
// Red-Black Tree (integer keys)
// Node = array: [key, val, left_id, right_id, parent_id, color]
// Tree = map: { root, cnt, nd }
// =====================================================

// The original tree stored linked node values. Under COW every lookup is a
// snapshot, so the mutable owner is an explicit key/value table instead
// (D3.3.1). The CD workload needs ordered iteration only, not tree balancing.

pn rbt_new() any {
    var keys = vec_new()
    var vals = vec_new()
    var table = { keys: null, vals: null }
    table.keys = keys
    table.vals = vals
    return table
}

pn rbt_find_node(tree, key: int) int {
    var keys = tree.keys
    var i = 0
    while (i < len(keys)) {
        if (keys[i] == key) {
            return i
        }
        i = i + 1
    }
    return NIL
}

pn rbt_nd(tree, id: int) array {
    return [tree.keys[id], tree.vals[id]]
}

pn rbt_put(var tree, key: int, value) any {
    var index = rbt_find_node(tree, key)
    var keys = tree.keys
    var vals = tree.vals
    if (index != NIL) {
        var old = vals[index]
        vals[index] = value
        tree.vals = vals
        return old
    }
    push(keys, key)
    push(vals, value)
    tree.keys = keys
    tree.vals = vals
    return null
}

pn rbt_get(tree, key: int) any {
    var index = rbt_find_node(tree, key)
    if (index == NIL) {
        return null
    }
    return tree.vals[index]
}

pn rbt_first(tree) int {
    if (len(tree.keys) == 0) {
        return NIL
    }
    return 0
}

pn rbt_successor(tree, id: int) int {
    var next = id + 1
    if (next >= len(tree.keys)) {
        return NIL
    }
    return next
}

pn rbt_remove(var tree, key: int) any {
    var index = rbt_find_node(tree, key)
    if (index == NIL) {
        return null
    }
    var keys = tree.keys
    var vals = tree.vals
    var old = vals[index]
    var i = index
    var tail_index = len(keys) - 1
    while (i < tail_index) {
        keys[i] = keys[i + 1]
        vals[i] = vals[i + 1]
        i = i + 1
    }
    tree.keys = slice(keys, 0, tail_index)
    tree.vals = slice(vals, 0, tail_index)
    return old
}

// =====================================================
// Vector2D key encoding
// =====================================================
pn v2d_key(x: int, y: int) int {
    var kx = int(x) + 1000
    var ky = int(y) + 1000
    var k = kx * 100000 + ky
    return k
}

// =====================================================
// Vector3D operations
// =====================================================
pn v3d_new(x, y, z) array {
    return [x, y, z]
}

// =====================================================
// Voxel hashing
// =====================================================
pn voxel_hash_xy(px: float, py: float, var out: array) int {
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
pn is_in_voxel(vx: int, vy: int, p1x: float, p1y: float,
        p2x: float, p2y: float) int {
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
pn recurse_draw(voxel_map, seen_tree, vx: int, vy: int,
        p1x: float, p1y: float, p2x: float, p2y: float,
        motion_idx: int) array {
    // The recursive frontier returns both owned tables explicitly. A nested
    // call cannot leave an updated COW root behind as a discarded snapshot.
    var voxels = voxel_map
    var seen = seen_tree
    var in_voxel = is_in_voxel(vx, vy, p1x, p1y, p2x, p2y)
    if (in_voxel == 0) {
        return [voxels, seen]
    }

    var voxel_key = v2d_key(vx, vy)
    var old_seen = rbt_put(seen, voxel_key, 1)
    if (old_seen != null) {
        return [voxels, seen]
    }

    var entries = rbt_get(voxels, voxel_key)
    if (entries == null) {
        entries = vec_new()
    }
    vec_add(entries, motion_idx)
    rbt_put(voxels, voxel_key, entries)

    var grid_size = GOOD_VOXEL_SIZE
    var step = recurse_draw(voxels, seen, vx - grid_size, vy, p1x, p1y, p2x, p2y, motion_idx)
    voxels = step[0]
    seen = step[1]
    step = recurse_draw(voxels, seen, vx + grid_size, vy, p1x, p1y, p2x, p2y, motion_idx)
    voxels = step[0]
    seen = step[1]
    step = recurse_draw(voxels, seen, vx, vy - grid_size, p1x, p1y, p2x, p2y, motion_idx)
    voxels = step[0]
    seen = step[1]
    step = recurse_draw(voxels, seen, vx, vy + grid_size, p1x, p1y, p2x, p2y, motion_idx)
    voxels = step[0]
    seen = step[1]
    step = recurse_draw(voxels, seen, vx - grid_size, vy - grid_size, p1x, p1y, p2x, p2y, motion_idx)
    voxels = step[0]
    seen = step[1]
    step = recurse_draw(voxels, seen, vx - grid_size, vy + grid_size, p1x, p1y, p2x, p2y, motion_idx)
    voxels = step[0]
    seen = step[1]
    step = recurse_draw(voxels, seen, vx + grid_size, vy - grid_size, p1x, p1y, p2x, p2y, motion_idx)
    voxels = step[0]
    seen = step[1]
    step = recurse_draw(voxels, seen, vx + grid_size, vy + grid_size, p1x, p1y, p2x, p2y, motion_idx)
    voxels = step[0]
    seen = step[1]
    return [voxels, seen]
}

// =====================================================
// findIntersection between two motions
// =====================================================
pn find_intersection(m1, m2) any {
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

        var sq = math.sqrt(discr)
        var a2 = 2 * a
        var t1 = (-b - sq) / a2
        var t2 = (-b + sq) / a2

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
    var dist = math.sqrt(pdx * pdx + pdy * pdy + pdz * pdz)
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
pn motion_new(cs: int, p1x, p1y, p1z, p2x, p2y, p2z) array {
    return [cs, p1x, p1y, p1z, p2x, p2y, p2z]
}

// =====================================================
// CD Benchmark main logic
// =====================================================

pn simulate_frame(numAircraft: int, tval: float) array {
    var frame = vec_new()
    var i = 0
    while (i < numAircraft) {
        var cs1 = i
        var px1 = tval
        var py1 = math.cos(tval) * 2 + i * 3
        var pz1 = 10
        var a1 = [cs1, px1, py1, pz1]
        vec_add(frame, a1)
        var cs2 = i + 1
        var py2 = math.sin(tval) * 2 + i * 3
        var a2 = [cs2, px1, py2, pz1]
        vec_add(frame, a2)
        i = i + 2
    }
    return frame
}

pn handle_new_frame(var stateTree, frame: array) int {
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
        var drawn = recurse_draw(voxelMap, motSeen, vvx, vvy, mp1x, mp1y, mp2x, mp2y, mi)
        voxelMap = drawn[0]
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

pn cd(numAircraft: int) int {
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
    var collisions = cd(100)
    var __t1 = clock()
    print("collisions=")
    print(collisions)
    print("\n")
    var ok = verify_result(collisions, 100)
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
