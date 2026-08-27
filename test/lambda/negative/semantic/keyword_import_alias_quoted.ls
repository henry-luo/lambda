// S16.10.1v2: there is NO quoted escape for a binding name. `import 'edit':`
// used to parse and create an unreachable binding — at a use site `'edit'.x`
// is a symbol member expression, and symbols never implicitly read bindings
// (S2.4.3), so the value was silently null.
import 'edit': .keyword_shadow_module
1
