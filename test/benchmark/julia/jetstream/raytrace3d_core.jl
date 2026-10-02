# port of test/benchmark/jetstream/raytrace3d.py; see ../LICENSE.md.
# JetStream Benchmark: 3d-raytrace (SunSpider) — Julia version
# Simple ray tracer — renders a scene with triangles, lighting, and reflections
# Original: Apple Inc.
#
function vec3(x, y, z)
    return [x, y, z]
end

function vec_add(v1, v2)
    return [add0(get0(v1, 0), get0(v2, 0)), add0(get0(v1, 1), get0(v2, 1)), add0(get0(v1, 2), get0(v2, 2))]
end

function vec_sub(v1, v2)
    return [(get0(v1, 0) - get0(v2, 0)), (get0(v1, 1) - get0(v2, 1)), (get0(v1, 2) - get0(v2, 2))]
end

function vec_scale(v, s)
    return [mul0(get0(v, 0), s), mul0(get0(v, 1), s), mul0(get0(v, 2), s)]
end

function vec_scalev(v1, v2)
    return [mul0(get0(v1, 0), get0(v2, 0)), mul0(get0(v1, 1), get0(v2, 1)), mul0(get0(v1, 2), get0(v2, 2))]
end

function vec_dot(v1, v2)
    return add0(add0(mul0(get0(v1, 0), get0(v2, 0)), mul0(get0(v1, 1), get0(v2, 1))), mul0(get0(v1, 2), get0(v2, 2)))
end

function vec_cross(v1, v2)
    return [(mul0(get0(v1, 1), get0(v2, 2)) - mul0(get0(v1, 2), get0(v2, 1))), (mul0(get0(v1, 2), get0(v2, 0)) - mul0(get0(v1, 0), get0(v2, 2))), (mul0(get0(v1, 0), get0(v2, 1)) - mul0(get0(v1, 1), get0(v2, 0)))]
end

function vec_length(v)
    return sqrt(add0(add0(mul0(get0(v, 0), get0(v, 0)), mul0(get0(v, 1), get0(v, 1))), mul0(get0(v, 2), get0(v, 2))))
end

function vec_normalise(v)
    len = vec_length(v)
    return [v[1] / len, v[2] / len, v[3] / len]
end

function vec_add_inplace(v1, v2)
    set0!(v1, 0, add0(get0(v1, 0), get0(v2, 0)))
    set0!(v1, 1, add0(get0(v1, 1), get0(v2, 1)))
    set0!(v1, 2, add0(get0(v1, 2), get0(v2, 2)))
    return v1
end

function transform_matrix(m, v)
    local x, y, z
    x = add0(add0(add0(mul0(get0(m, 0), get0(v, 0)), mul0(get0(m, 1), get0(v, 1))), mul0(get0(m, 2), get0(v, 2))), get0(m, 3))
    y = add0(add0(add0(mul0(get0(m, 4), get0(v, 0)), mul0(get0(m, 5), get0(v, 1))), mul0(get0(m, 6), get0(v, 2))), get0(m, 7))
    z = add0(add0(add0(mul0(get0(m, 8), get0(v, 0)), mul0(get0(m, 9), get0(v, 1))), mul0(get0(m, 10), get0(v, 2))), get0(m, 11))
    return [x, y, z]
end

function invert_matrix(m)
    local h, i, temp, tx, ty, tz, vv
    tx = -(get0(m, 3))
    ty = -(get0(m, 7))
    tz = -(get0(m, 11))
    temp = slice0(m, nothing, nothing, nothing)
    for h in range0(3)
        for vv in range0(3)
            set0!(temp, add0(h, mul0(vv, 4)), get0(m, add0(vv, mul0(h, 4))))
        end
    end
    for i in range0(12)
        set0!(m, i, get0(temp, i))
    end
    set0!(m, 3, add0(add0(mul0(tx, get0(m, 0)), mul0(ty, get0(m, 1))), mul0(tz, get0(m, 2))))
    set0!(m, 7, add0(add0(mul0(tx, get0(m, 4)), mul0(ty, get0(m, 5))), mul0(tz, get0(m, 6))))
    set0!(m, 11, add0(add0(mul0(tx, get0(m, 8)), mul0(ty, get0(m, 9))), mul0(tz, get0(m, 10))))
    return m
end

function create_triangle(p1, p2, p3)
    local ax, axis, ay, az, det, edge1, edge2, inv_na, n_norm, nd, normal, nu, nv, u, u1, u2, v, v1, v2
    edge1 = vec_sub(p3, p1)
    edge2 = vec_sub(p2, p1)
    normal = vec_cross(edge1, edge2)
    ax = abs(get0(normal, 0))
    ay = abs(get0(normal, 1))
    az = abs(get0(normal, 2))
    if truth0(((ax > ay)))
        axis = (truth0(((ax > az))) ? 0 : 2)
    else
        axis = (truth0(((ay > az))) ? 1 : 2)
    end
    u = mod(add0(axis, 1), 3)
    v = mod(add0(axis, 2), 3)
    u1 = get0(edge1, u)
    v1 = get0(edge1, v)
    u2 = get0(edge2, u)
    v2 = get0(edge2, v)
    n_norm = vec_normalise(normal)
    inv_na = (1.0 / get0(normal, axis))
    nu = (get0(normal, u) / get0(normal, axis))
    nv = (get0(normal, v) / get0(normal, axis))
    nd = (vec_dot(normal, p1) / get0(normal, axis))
    det = (mul0(u1, v2) - mul0(v1, u2))
    return Dict{Any,Any}("axis"=>axis, "normal"=>n_norm, "nu"=>nu, "nv"=>nv, "nd"=>nd, "eu"=>get0(p1, u), "ev"=>get0(p1, v), "nu1"=>(u1 / det), "nv1"=>(-(v1) / det), "nu2"=>(v2 / det), "nv2"=>(-(u2) / det), "material"=>[0.7, 0.7, 0.7])
end

function triangle_intersect(tri, orig, direction, near, far)
    axis = tri["axis"] + 1
    u, v = mod(axis, 3) + 1, mod(axis + 1, 3) + 1
    d = direction[axis] + tri["nu"] * direction[u] + tri["nv"] * direction[v]
    t = (tri["nd"] - orig[axis] - tri["nu"] * orig[u] - tri["nv"] * orig[v]) / d
    (t < near || t > far) && return nothing
    pu, pv = orig[u] + t * direction[u] - tri["eu"], orig[v] + t * direction[v] - tri["ev"]
    a2 = pv * tri["nu1"] + pu * tri["nv1"]
    a2 < 0 && return nothing
    a3 = pu * tri["nu2"] + pv * tri["nv2"]
    (a3 < 0 || a2 + a3 > 1) && return nothing
    return t
end

function create_scene(triangles)
    return Dict{Any,Any}("triangles"=>triangles, "lights"=>Any[], "ambient"=>[0.0, 0.0, 0.0], "background"=>[0.8, 0.8, 1.0], "n_lights"=>0, "n_triangles"=>Base.length(triangles))
end

function scene_add_light(scene, pos, colour)
    m_append(get0(scene, "lights"), Dict{Any,Any}("pos"=>pos, "colour"=>colour))
    set0!(scene, "n_lights", add0(get0(scene, "n_lights"), 1))
    return nothing
end

function scene_blocked(scene, origin, direction, far)
    for tri in scene["triangles"]
        d = triangle_intersect(tri, origin, direction, 0.0001, far)
        if d !== nothing && !(d > far || d < 0.0001); return true; end
    end
    return false
end
function scene_intersect(scene, origin, direction, near=NaN, far=NaN)
    # initial rays omit near/far in JS; NaN preserves those comparison semantics.
    closest = nothing
    for tri in scene["triangles"]
        d = triangle_intersect(tri, origin, direction, near, far)
        if d === nothing || d > far || d < near; continue; end
        far, closest = d, tri
    end
    closest === nothing && return copy(scene["background"])
    normal = closest["normal"]
    hit = vec_add(origin, vec_scale(direction, far))
    if vec_dot(direction, normal) > 0; normal = -normal; end
    colour = closest["material"]
    if get(closest, "floor", false)
        x = mod(mod(hit[1] / 32, 2) + 2, 2)
        z = mod(mod(hit[3] / 32 + 0.3, 2) + 2, 2)
        if (x < 1) != (z < 1)
            reflection = vec_add(vec_scale(normal, -2 * vec_dot(direction, normal)), direction)
            return scene_intersect(scene, hit, reflection, 0.0001, 1000000.0)
        end
        colour = [0.0, 0.4, 0.0]
    end
    light_sum = copy(scene["ambient"])
    for light in scene["lights"]
        to_light = vec_sub(light["pos"], hit)
        distance = vec_length(to_light)
        to_light .*= 1.0 / distance
        scene_blocked(scene, hit, to_light, distance - 0.0001) && continue
        nl = vec_dot(normal, to_light)
        nl > 0 && vec_add_inplace(light_sum, vec_scale(light["colour"], nl))
    end
    return vec_scalev(light_sum, colour)
end

# mutating normalization in Camera/renderRows divides each component directly.
function vec_normalise_inplace(v)
    len = vec_length(v)
    for i in 1:3; v[i] /= len; end
    return v
end

function create_camera(origin, lookat, up)
    local d0, d1, d2, d3, m, neg_z, xaxis, yaxis, zaxis
    zaxis = vec_normalise_inplace(vec_sub(lookat, origin))
    xaxis = vec_normalise_inplace(vec_cross(up, zaxis))
    neg_z = [-(get0(zaxis, 0)), -(get0(zaxis, 1)), -(get0(zaxis, 2))]
    yaxis = vec_normalise_inplace(vec_cross(xaxis, neg_z))
    m = mul0([0.0], 12)
    set0!(m, 0, get0(xaxis, 0))
    set0!(m, 1, get0(xaxis, 1))
    set0!(m, 2, get0(xaxis, 2))
    set0!(m, 4, get0(yaxis, 0))
    set0!(m, 5, get0(yaxis, 1))
    set0!(m, 6, get0(yaxis, 2))
    set0!(m, 8, get0(zaxis, 0))
    set0!(m, 9, get0(zaxis, 1))
    set0!(m, 10, get0(zaxis, 2))
    m = invert_matrix(m)
    set0!(m, 3, 0.0)
    set0!(m, 7, 0.0)
    set0!(m, 11, 0.0)
    d0 = vec_normalise([-(0.7), 0.7, 1.0])
    d1 = vec_normalise([0.7, 0.7, 1.0])
    d2 = vec_normalise([0.7, -(0.7), 1.0])
    d3 = vec_normalise([-(0.7), -(0.7), 1.0])
    d0 = transform_matrix(m, d0)
    d1 = transform_matrix(m, d1)
    d2 = transform_matrix(m, d2)
    d3 = transform_matrix(m, d3)
    return Dict{Any,Any}("origin"=>origin, "d0"=>d0, "d1"=>d1, "d2"=>d2, "d3"=>d3)
end

function render_scene(camera, scene, size)
    pixels = [Vector{Float64}[] for _ in 1:size]
    d0, d1, d2, d3 = (camera[k] for k in ("d0", "d1", "d2", "d3"))
    for y in 0:size-1
        yf = y / size
        ray0_dir = vec_add(vec_scale(d0, yf), vec_scale(d3, 1 - yf))
        ray1_dir = vec_add(vec_scale(d1, yf), vec_scale(d2, 1 - yf))
        for x in 0:size-1
            xf = x / size
            origin = vec_add(vec_scale(camera["origin"], xf), vec_scale(camera["origin"], 1 - xf))
            direction = vec_normalise_inplace(vec_add(vec_scale(ray0_dir, xf), vec_scale(ray1_dir, 1 - xf)))
            push!(pixels[y + 1], scene_intersect(scene, origin, direction))
        end
    end
    return pixels
end

function raytrace_scene()
    local bbl, bbr, bfl, bfr, cam, fbl, fbr, ffl, ffr, scene, tbl, tbr, tfl, tfr, triangles
    tfl = vec3(-(10.0), 10.0, -(10.0))
    tfr = vec3(10.0, 10.0, -(10.0))
    tbl = vec3(-(10.0), 10.0, 10.0)
    tbr = vec3(10.0, 10.0, 10.0)
    bfl = vec3(-(10.0), -(10.0), -(10.0))
    bfr = vec3(10.0, -(10.0), -(10.0))
    bbl = vec3(-(10.0), -(10.0), 10.0)
    bbr = vec3(10.0, -(10.0), 10.0)
    triangles = [create_triangle(tfl, tfr, bfr), create_triangle(tfl, bfr, bfl), create_triangle(tbl, tbr, bbr), create_triangle(tbl, bbr, bbl), create_triangle(tbl, tfl, bbl), create_triangle(tfl, bfl, bbl), create_triangle(tbr, tfr, bbr), create_triangle(tfr, bfr, bbr), create_triangle(tbl, tbr, tfr), create_triangle(tbl, tfr, tfl), create_triangle(bbl, bbr, bfr), create_triangle(bbl, bfr, bfl)]
    ffl = vec3(-(1000.0), -(30.0), -(1000.0))
    ffr = vec3(1000.0, -(30.0), -(1000.0))
    fbl = vec3(-(1000.0), -(30.0), 1000.0)
    fbr = vec3(1000.0, -(30.0), 1000.0)
    m_append(triangles, create_triangle(fbl, fbr, ffr))
    m_append(triangles, create_triangle(fbl, ffr, ffl))
    get0(triangles, 12)["floor"] = true
    get0(triangles, 13)["floor"] = true
    scene = create_scene(triangles)
    scene_add_light(scene, vec3(20.0, 38.0, -(22.0)), [0.7, 0.3, 0.3])
    scene_add_light(scene, vec3(-(23.0), 40.0, 17.0), [0.7, 0.3, 0.3])
    scene_add_light(scene, vec3(23.0, 20.0, 17.0), [0.7, 0.7, 0.7])
    set0!(scene, "ambient", [0.1, 0.1, 0.1])
    cam = create_camera(vec3(-(40.0), 40.0, 40.0), vec3(0.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0))
    return render_scene(cam, scene, 30)
end

const CANVAS_PREFIX = "<canvas id=\"renderCanvas\" width=\"30px\" height=\"30px\"></canvas><script>\nvar pixels = ["
const CANVAS_SUFFIX = "];\n    var canvas = document.getElementById(\"renderCanvas\").getContext(\"2d\");\n\n\n    var size = 30;\n    canvas.fillStyle = \"red\";\n    canvas.fillRect(0, 0, size, size);\n    canvas.scale(1, -1);\n    canvas.translate(0, -size);\n\n    if (!canvas.setFillColor)\n        canvas.setFillColor = function(r, g, b, a) {\n            this.fillStyle = \"rgb(\"+[Math.floor(r * 255), Math.floor(g * 255), Math.floor(b * 255)]+\")\";\n    }\n\nfor (var y = 0; y < size; y++) {\n  for (var x = 0; x < size; x++) {\n    var l = pixels[y][x];\n    canvas.setFillColor(l[0], l[1], l[2], 1);\n    canvas.fillRect(x, y, 1, 1);\n  }\n}</script>"

js_number(value) = value == trunc(value) ? string(trunc(Int, value)) : string(value)
function canvas_commands(pixels)
    io = IOBuffer()
    print(io, CANVAS_PREFIX)
    for row in pixels
        print(io, "[")
        for pixel in row; print(io, "[", join(js_number.(pixel), ","), "],"); end
        print(io, "],")
    end
    print(io, CANVAS_SUFFIX)
    return String(take!(io))
end
