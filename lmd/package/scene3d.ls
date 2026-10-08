// D7.2.4/S12.1.1v2: a source package builds values; the Radiant host owns GPU effects.
import model: lambda.scene3d.model
import transforms: lambda.scene3d.transform
import clips: lambda.scene3d.animation
import assets: lambda.scene3d.asset

pub type Vector3 = [number, number, number]
pub type Matrix4 = number[]
pub type Scene = <scene3d>

pub fn scene(children, options = {}) element => <scene3d *: options, *children>
pub fn group(children, options = {}) element => <group *: options, *children>
pub fn camera(options = {}) element => <camera type: 'perspective', fov: 50.0, near: 0.1,
    far: 1000.0, position: [0.0, 0.0, 4.0], target: [0.0, 0.0, 0.0], *: options>
pub fn light(kind, options = {}) element => <light type: kind, color: "#ffffff", intensity: 1.0, *: options>
pub fn box(size = [1.0, 1.0, 1.0], options = {}) element => <geometry type: 'box', size: size, *: options>
pub fn plane(size = [1.0, 1.0, 1.0], options = {}) element => <geometry type: 'plane', size: size, *: options>
pub fn geometry(positions, options = {}) element => <geometry type: 'buffer', positions: positions, *: options>
pub fn material(kind = 'basic', options = {}) element => <material type: kind, color: "#ffffff", opacity: 1.0, *: options>
pub fn texture(source, options = {}) element => <texture src: source, *: options>
pub fn mesh(geometry, material, options = {}) element => <mesh *: options, *[geometry, material]>
pub fn resources(children) element => <resources *children>
pub fn clip(label, tracks, options = {}) element => clips.clip(label, tracks, options)
pub fn track(path, times, values, kind = 'number', options = {}) element => clips.track(path, times, values, kind, options)
pub fn load(source, options = {}) element^ => assets.load(source, options)^
pub fn normalize(scene) element^ => model.normalize(scene)^
pub fn validate(scene) bool^ { let checked = model.normalize(scene)^; true }
pub fn transform(position = [0.0, 0.0, 0.0], rotation = [0.0, 0.0, 0.0], scale = [1.0, 1.0, 1.0]) array^ =>
    transforms.matrix(position, rotation, scale)^
pub fn multiply(a, b) array^ => transforms.multiply(a, b)^
pub fn point(matrix, position) array^ => transforms.point(matrix, position)^
