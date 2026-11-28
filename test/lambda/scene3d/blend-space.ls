// Built-in display-space blending is opt-in and needs an opaque background.
import s: lambda.scene3d
fn check(space, background) any^ => s.validate(s.scene([
    s.camera({id:"main"}), s.mesh(s.plane(), s.material('basic', {transparent:true, opacity:0.5, 'blend-space':space}))^],
    {background:background})) ^ { ^.message }
let shader = s.shader_material("void main(){gl_Position=vec4(position,1.0);}",
    "void main(){fragment_color=vec4(1.0);}", {}, {'blend-space':'srgb'})
let shader_result = s.validate(s.scene([s.camera({id:"main"}), s.mesh(s.plane(), shader)^], {background:"#ffffff"})) ^ { ^.message };
[check('srgb', "#ffffff")^ == true, check("srgb", "#ffffff")^ == true, check('linear', "transparent")^ == true,
 contains(check('other', "#ffffff")^, "invalid material blend space"),
 contains(check(12, "#ffffff")^, "invalid material blend space"),
 contains(check('srgb', "transparent")^, "opaque scene background"),
 contains(check("srgb", "transparent")^, "opaque scene background"),
 contains(shader_result, "not supported by shader materials")]
