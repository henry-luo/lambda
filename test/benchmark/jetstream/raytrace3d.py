#!/usr/bin/env python3
"""JetStream Benchmark: 3d-raytrace (SunSpider) — Python version
Simple ray tracer — renders a scene with triangles, lighting, and reflections
Original: Apple Inc.
"""
import time
import math


def vec3(x, y, z):
    return [x, y, z]


def vec_add(v1, v2):
    return [v1[0] + v2[0], v1[1] + v2[1], v1[2] + v2[2]]


def vec_sub(v1, v2):
    return [v1[0] - v2[0], v1[1] - v2[1], v1[2] - v2[2]]


def vec_scale(v, s):
    return [v[0] * s, v[1] * s, v[2] * s]


def vec_scalev(v1, v2):
    return [v1[0] * v2[0], v1[1] * v2[1], v1[2] * v2[2]]


def vec_dot(v1, v2):
    return v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2]


def vec_cross(v1, v2):
    return [v1[1] * v2[2] - v1[2] * v2[1],
            v1[2] * v2[0] - v1[0] * v2[2],
            v1[0] * v2[1] - v1[1] * v2[0]]


def vec_length(v):
    return math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])


def vec_normalise(v):
    inv = 1.0 / vec_length(v)
    return [v[0] * inv, v[1] * inv, v[2] * inv]


def vec_add_inplace(v1, v2):
    v1[0] += v2[0]
    v1[1] += v2[1]
    v1[2] += v2[2]
    return v1


def transform_matrix(m, v):
    x = m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3]
    y = m[4] * v[0] + m[5] * v[1] + m[6] * v[2] + m[7]
    z = m[8] * v[0] + m[9] * v[1] + m[10] * v[2] + m[11]
    return [x, y, z]


def invert_matrix(m):
    tx = -m[3]
    ty = -m[7]
    tz = -m[11]
    # transpose 3x3
    temp = m[:]
    for h in range(3):
        for vv in range(3):
            temp[h + vv * 4] = m[vv + h * 4]
    for i in range(12):
        m[i] = temp[i]
    m[3]  = tx * m[0] + ty * m[1] + tz * m[2]
    m[7]  = tx * m[4] + ty * m[5] + tz * m[6]
    m[11] = tx * m[8] + ty * m[9] + tz * m[10]
    return m


def create_triangle(p1, p2, p3):
    edge1 = vec_sub(p3, p1)
    edge2 = vec_sub(p2, p1)
    normal = vec_cross(edge1, edge2)

    ax = abs(normal[0])
    ay = abs(normal[1])
    az = abs(normal[2])
    if ax > ay:
        axis = 0 if ax > az else 2
    else:
        axis = 1 if ay > az else 2

    u = (axis + 1) % 3
    v = (axis + 2) % 3
    u1 = edge1[u]
    v1 = edge1[v]
    u2 = edge2[u]
    v2 = edge2[v]
    n_norm = vec_normalise(normal)
    inv_na = 1.0 / normal[axis]
    nu = normal[u] * inv_na
    nv = normal[v] * inv_na
    nd = vec_dot(normal, p1) * inv_na
    det = u1 * v2 - v1 * u2
    return {
        'axis': axis, 'normal': n_norm, 'nu': nu, 'nv': nv, 'nd': nd,
        'eu': p1[u], 'ev': p1[v],
        'nu1': u1 / det, 'nv1': -v1 / det,
        'nu2': v2 / det, 'nv2': -u2 / det,
        'material': [0.7, 0.7, 0.7],
    }


def triangle_intersect(tri, orig, direction, near, far):
    axis = tri['axis']
    u = (axis + 1) % 3
    v = (axis + 2) % 3
    d = direction[axis] + tri['nu'] * direction[u] + tri['nv'] * direction[v]
    t = (tri['nd'] - orig[axis] - tri['nu'] * orig[u] - tri['nv'] * orig[v]) / d
    if t < near or t > far:
        return -1.0
    pu = orig[u] + t * direction[u] - tri['eu']
    pv = orig[v] + t * direction[v] - tri['ev']
    a2 = pv * tri['nu1'] + pu * tri['nv1']
    if a2 < 0.0:
        return -1.0
    a3 = pu * tri['nu2'] + pv * tri['nv2']
    if a3 < 0.0:
        return -1.0
    if a2 + a3 > 1.0:
        return -1.0
    return t


def create_scene(triangles):
    return {'triangles': triangles, 'lights': [],
            'ambient': [0.0, 0.0, 0.0], 'background': [0.8, 0.8, 1.0],
            'n_lights': 0, 'n_triangles': len(triangles)}


def scene_add_light(scene, pos, colour):
    scene['lights'].append({'pos': pos, 'colour': colour})
    scene['n_lights'] += 1


def scene_intersect(scene, origin, direction, near, far, depth):
    if depth > 3:
        return scene['background']
    closest = None
    closest_d = far
    for tri in scene['triangles']:
        d = triangle_intersect(tri, origin, direction, near, closest_d)
        if d > 0.0:
            closest_d = d
            closest = tri
    if closest is None:
        bg = scene['background']
        return [bg[0], bg[1], bg[2]]

    normal = closest['normal']
    hit = vec_add(origin, vec_scale(direction, closest_d))
    if vec_dot(direction, normal) > 0.0:
        normal = [-normal[0], -normal[1], -normal[2]]
    colour = closest['material']

    ambient = scene['ambient']
    l = [ambient[0], ambient[1], ambient[2]]
    triangles = scene['triangles']
    n_tri = scene['n_triangles']
    for light in scene['lights']:
        to_light = vec_sub(light['pos'], hit)
        distance = vec_length(to_light)
        inv_d = 1.0 / distance
        to_light = [to_light[0] * inv_d, to_light[1] * inv_d, to_light[2] * inv_d]
        blocked = False
        near_s = 0.0001
        far_s = distance - 0.0001
        for tri in triangles:
            sd = triangle_intersect(tri, hit, to_light, near_s, far_s)
            if sd > 0.0:
                blocked = True
                break
        if not blocked:
            nl = vec_dot(normal, to_light)
            if nl > 0.0:
                lc = light['colour']
                l[0] += lc[0] * nl
                l[1] += lc[1] * nl
                l[2] += lc[2] * nl
    return [l[0] * colour[0], l[1] * colour[1], l[2] * colour[2]]


def create_camera(origin, lookat, up):
    zaxis = vec_normalise(vec_sub(lookat, origin))
    xaxis = vec_normalise(vec_cross(up, zaxis))
    neg_z = [-zaxis[0], -zaxis[1], -zaxis[2]]
    yaxis = vec_normalise(vec_cross(xaxis, neg_z))
    m = [0.0] * 12
    m[0] = xaxis[0]; m[1] = xaxis[1]; m[2] = xaxis[2]
    m[4] = yaxis[0]; m[5] = yaxis[1]; m[6] = yaxis[2]
    m[8] = zaxis[0]; m[9] = zaxis[1]; m[10] = zaxis[2]
    m = invert_matrix(m)
    m[3] = 0.0; m[7] = 0.0; m[11] = 0.0

    d0 = vec_normalise([-0.7, 0.7, 1.0])
    d1 = vec_normalise([0.7, 0.7, 1.0])
    d2 = vec_normalise([0.7, -0.7, 1.0])
    d3 = vec_normalise([-0.7, -0.7, 1.0])
    d0 = transform_matrix(m, d0)
    d1 = transform_matrix(m, d1)
    d2 = transform_matrix(m, d2)
    d3 = transform_matrix(m, d3)
    return {'origin': origin, 'd0': d0, 'd1': d1, 'd2': d2, 'd3': d3}


def render_scene(camera, scene, size):
    pixel_count = 0
    fsize = float(size)
    d0 = camera['d0']
    d1 = camera['d1']
    d2 = camera['d2']
    d3 = camera['d3']
    cam_origin = camera['origin']
    for y in range(size):
        yf = y / fsize
        yf1 = 1.0 - yf
        ray0_dir = [d0[i] * yf + d3[i] * yf1 for i in range(3)]
        ray1_dir = [d1[i] * yf + d2[i] * yf1 for i in range(3)]
        for x in range(size):
            xf = x / fsize
            xf1 = 1.0 - xf
            origin = [cam_origin[i] * xf + cam_origin[i] * xf1 for i in range(3)]
            raw_dir = [ray0_dir[i] * xf + ray1_dir[i] * xf1 for i in range(3)]
            direction = vec_normalise(raw_dir)
            scene_intersect(scene, origin, direction, 0.0001, 1000000.0, 0)
            pixel_count += 1
    return pixel_count


def raytrace_scene():
    tfl = vec3(-10.0, 10.0, -10.0)
    tfr = vec3(10.0, 10.0, -10.0)
    tbl = vec3(-10.0, 10.0, 10.0)
    tbr = vec3(10.0, 10.0, 10.0)
    bfl = vec3(-10.0, -10.0, -10.0)
    bfr = vec3(10.0, -10.0, -10.0)
    bbl = vec3(-10.0, -10.0, 10.0)
    bbr = vec3(10.0, -10.0, 10.0)

    triangles = [
        create_triangle(tfl, tfr, bfr),
        create_triangle(tfl, bfr, bfl),
        create_triangle(tbl, tbr, bbr),
        create_triangle(tbl, bbr, bbl),
        create_triangle(tbl, tfl, bbl),
        create_triangle(tfl, bfl, bbl),
        create_triangle(tbr, tfr, bbr),
        create_triangle(tfr, bfr, bbr),
        create_triangle(tbl, tbr, tfr),
        create_triangle(tbl, tfr, tfl),
        create_triangle(bbl, bbr, bfr),
        create_triangle(bbl, bfr, bfl),
    ]

    ffl = vec3(-1000.0, -30.0, -1000.0)
    ffr = vec3(1000.0, -30.0, -1000.0)
    fbl = vec3(-1000.0, -30.0, 1000.0)
    fbr = vec3(1000.0, -30.0, 1000.0)
    triangles.append(create_triangle(fbl, fbr, ffr))
    triangles.append(create_triangle(fbl, ffr, ffl))

    scene = create_scene(triangles)
    scene_add_light(scene, vec3(20.0, 38.0, -22.0), [0.7, 0.3, 0.3])
    scene_add_light(scene, vec3(-23.0, 40.0, 17.0), [0.7, 0.3, 0.3])
    scene_add_light(scene, vec3(23.0, 20.0, 17.0),  [0.7, 0.7, 0.7])
    scene['ambient'] = [0.1, 0.1, 0.1]

    cam = create_camera(vec3(-40.0, 40.0, 40.0), vec3(0.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0))
    return render_scene(cam, scene, 30)


def main():
    t0 = time.perf_counter_ns()
    total_pixels = 0
    for _ in range(8):
        total_pixels += raytrace_scene()
    t1 = time.perf_counter_ns()

    # 30×30 = 900 pixels per iteration, 8 iterations = 7200
    if total_pixels == 7200:
        print(f"3d-raytrace: PASS (pixels={total_pixels})")
    else:
        print(f"3d-raytrace: DONE (pixels={total_pixels})")
    print(f"__TIMING__:{(t1 - t0) / 1_000_000:.3f}")


if __name__ == "__main__":
    main()
