// S12.1.1v3 / D9.3.4: ordered color passes are pure data; Radiant owns execution.
pub fn chain(children) element => <postprocessing *children>
pub fn effect(kind, options = {}) element => <effect type: kind, *: options>
pub fn shader(fragment, uniforms = {}, options = {}) element =>
    <effect type: 'shader', fragment: fragment, uniforms: uniforms, *: options>
