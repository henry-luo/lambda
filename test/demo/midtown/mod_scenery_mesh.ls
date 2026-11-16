// Mesh buffers for the scenery beyond the city, coloured per vertex (in linear light) so each kind is
// one draw: pines, broadleaf trees and hedgerow bushes, farmhouses with barns and silos, wind turbine
// towers and rotors, the lighthouse, sea rocks and sailboats. Modelled at the origin facing +Z.
import u: .mod_util
import m: .mod_mesh
import prim: lambda.procart.primitive
import shapes: lambda.scene3d.shape
import t: lambda.scene3d.transform

fn lin(c) => [for (x in c) math.pow(x, 2.2)]
fn dye(rgb, parts) any^ => m.tinted(m.merge(parts), lin(rgb))
fn ball(x, y, z, sx, sy, sz, rings = 5, segments = 8) any^ => (let b = shapes.sphere(rings, segments),
    m.placed({positions: b.positions, normals: b.normals, uvs: b.uvs, indices: b.indices}, t.compose([x, y, z], [0.0, 0.0, 0.0, 1.0], [sx, sy, sz])))
fn cone(y0, y1, r0, r1, segments = 8) any^ => m.post(0.0, 0.0, y0, y1, r0, r1, segments)

// ---- trees: a pine of three needle tiers, a broadleaf of clustered crowns, a bush (about 10 m tall at scale 1)
pub fn pine() any^ => m.merge([dye([0.36, 0.25, 0.17], [cone(0.0, 3.0, 0.26, 0.16, 6)]),
    dye([0.25, 0.37, 0.21], [cone(1.8, 6.2, 2.5, 0.25)]), dye([0.28, 0.41, 0.23], [cone(4.0, 8.6, 1.95, 0.2)]),
    dye([0.32, 0.45, 0.25], [cone(6.3, 11.0, 1.25, 0.05)])])
pub fn broadleaf() any^ => m.merge([dye([0.4, 0.3, 0.21], [cone(0.0, 3.6, 0.32, 0.2, 6)]),
    dye([0.38, 0.52, 0.22], [ball(0.0, 5.6, 0.0, 4.6, 3.8, 4.6, 4, 7)]), dye([0.45, 0.58, 0.26], [ball(1.0, 6.9, 0.6, 3.4, 2.9, 3.4, 4, 7)]),
    dye([0.34, 0.48, 0.2], [ball(-1.1, 6.4, -0.7, 3.2, 2.7, 3.2, 4, 7)])])
pub fn bush() any^ => dye([0.31, 0.45, 0.2], [ball(0.0, 0.7, 0.0, 2.6, 1.7, 2.2, 4, 7)])
pub fn rock() any^ => dye([0.42, 0.4, 0.37], [ball(0.0, 0.0, 0.0, 1.0, 0.7, 0.85, 4, 6)])

// ---- a farmstead: a farmhouse facing +Z with a pitched roof, a red barn behind it, a silo beside the barn
fn gable(x0, x1, z0, z1, y0, ridge) any^ {
    let mx = (x0 + x1) / 2.0;
    m.merge([m.quad([x0, y0, z1], [x1, y0, z1], [mx, ridge, z1], [mx, ridge, z1]), m.quad([x1, y0, z0], [x0, y0, z0], [mx, ridge, z0], [mx, ridge, z0]),
        m.quad([x1, y0, z1], [x1, y0, z0], [mx, ridge, z0], [mx, ridge, z1]), m.quad([x0, y0, z0], [x0, y0, z1], [mx, ridge, z1], [mx, ridge, z0])])
}
fn window_row(x0, x1, y, z, n) any^ => [for (k in 0 to (n - 1), let x = x0 + (k + 0.5) * (x1 - x0) / n) m.block(x, y, z, 1.1, 1.3, 0.08)]
pub fn farmstead() any^ => m.merge([
    dye([0.88, 0.85, 0.78], [m.cuboid(-5.0, 0.0, -4.0, 5.0, 5.6, 4.0)]),
    dye([0.36, 0.2, 0.17], [gable(-5.6, 5.6, -4.6, 4.6, 5.6, 9.0)]),
    dye([0.2, 0.23, 0.27], [*window_row(-4.0, 4.0, 1.2, 4.02, 3), *window_row(-4.0, 4.0, 3.6, 4.02, 3), m.block(0.0, 0.0, 4.05, 1.2, 2.2, 0.1)]),
    dye([0.62, 0.18, 0.14], [m.cuboid(-7.0, 0.0, -33.0, 7.0, 7.0, -15.0)]),
    dye([0.3, 0.3, 0.32], [gable(-7.6, 7.6, -33.4, -14.6, 7.0, 11.5)]),
    dye([0.92, 0.9, 0.86], [m.cuboid(-2.4, 0.0, -14.95, 2.4, 4.6, -14.9), m.cuboid(-7.05, 6.6, -33.0, 7.05, 7.0, -15.0)]),
    dye([0.75, 0.76, 0.78], [m.post(10.5, -22.0, 0.0, 15.0, 2.8, 2.8, 12)]),
    dye([0.6, 0.62, 0.66], [ball(10.5, 15.0, -22.0, 5.6, 3.0, 5.6, 4, 12)])])

// ---- a wind turbine: a tapered white tower and nacelle (static), and a three-bladed rotor about its
// hub, in the rotor's own frame turning about +Z
pub fn turbine_tower(hub) any^ => m.merge([dye([0.92, 0.93, 0.94], [cone(0.0, hub, 2.1, 1.1, 12), m.block(0.0, hub - 1.4, -1.2, 2.6, 2.8, 7.0)]),
    dye([0.55, 0.57, 0.6], [cone(-0.5, 0.6, 3.2, 3.2, 12)])])
pub fn turbine_rotor() any^ => m.merge([dye([0.93, 0.94, 0.95], [*[for (k in 0 to 2, let a = k * u.TAU / 3.0)
        m.placed(m.merge([m.cuboid(-0.9, 0.0, -0.18, 0.9, 6.0, 0.18), m.cuboid(-0.6, 6.0, -0.12, 0.6, 18.0, 0.12), m.cuboid(-0.3, 18.0, -0.06, 0.3, 27.0, 0.06)]),
            t.compose([0.0, 0.0, 0.0], [0.0, 0.0, sin(a / 2.0), cos(a / 2.0)], [1.0, 1.0, 1.0]))^]]),
    dye([0.88, 0.89, 0.9], [ball(0.0, 0.0, 0.6, 1.4, 1.4, 2.0, 5, 10)])])

// ---- the lighthouse: a white tower banded red, a gallery and a lantern room under a dark cap
pub fn lighthouse() any^ => m.merge([
    dye([0.84, 0.82, 0.78], [m.block(0.0, 0.0, 0.0, 8.0, 2.4, 8.0), m.block(-6.0, 0.0, 0.0, 6.0, 4.0, 5.0)]),
    dye([0.95, 0.95, 0.93], [cone(2.4, 8.0, 2.9, 2.6, 14), cone(12.0, 16.0, 2.3, 2.1, 14), cone(20.0, 22.4, 1.9, 1.8, 14)]),
    dye([0.75, 0.13, 0.12], [cone(8.0, 12.0, 2.6, 2.3, 14), cone(16.0, 20.0, 2.1, 1.9, 14)]),
    dye([0.18, 0.19, 0.2], [cone(22.4, 22.9, 3.0, 3.0, 14), cone(25.4, 27.4, 2.0, 0.2, 14), cone(27.4, 29.0, 0.08, 0.08, 4)])])
pub fn lantern() any^ => dye([1.0, 0.95, 0.8], [cone(22.9, 25.4, 1.6, 1.6, 14)])

// ---- a sailboat: a white hull, a cabin, a mast and two sails (facing +Z)
pub fn hull() any^ => m.merge([dye([0.93, 0.93, 0.9], [m.hexa(m.ring(-0.6, -4.0, 0.6, 3.6, -0.6), m.ring(-1.4, -4.4, 1.4, 4.4, 0.7))]),
    dye([0.7, 0.55, 0.38], [m.cuboid(-1.3, 0.7, -4.2, 1.3, 0.78, 3.4)]), dye([0.88, 0.88, 0.86], [m.cuboid(-0.9, 0.78, -2.2, 0.9, 1.5, 0.8)]),
    dye([0.75, 0.76, 0.78], [m.post(0.0, 0.6, 0.78, 12.0, 0.08, 0.06, 5)])])
pub fn sails() any^ => dye([0.97, 0.96, 0.92], [m.tri([0.0, 1.6, 0.7], [0.0, 11.6, 0.65], [0.0, 1.6, -3.6]), m.tri([0.0, 1.6, -3.6], [0.0, 11.6, 0.65], [0.0, 1.6, 0.7]),
    m.tri([0.0, 1.5, 1.1], [0.0, 10.6, 0.8], [0.0, 1.5, 4.1]), m.tri([0.0, 1.5, 4.1], [0.0, 10.6, 0.8], [0.0, 1.5, 1.1])])
