// mod_drawing_schema.ls — schema entries for the drawing layer
// (Radiant Rich Editor, Stage 4 — §3.1, §13.1).
//
// Lambda port of test/editor-js/src/drawing/schema.ts. These entries extend a
// flow-doc schema so a document can carry inline <drawing> blocks. Each entry
// has the same shape as the md_schema entries (see mod_md_schema.ls).
//
// Two new roles separate drawing children from flow-doc roles:
//   'drawing-container'  — a <layer> (z-stack)
//   'drawing-object'     — shape / connector / group / text-frame / label
// All drawing objects share the role 'drawing-object', so a content term
// {role: 'drawing-object'} matches any of them directly — no role-compatibility
// extension is needed in the validator (mod_edit_schema.ls).

// Allowed shape kinds (symbols, validated on the `kind` attr).
pub shape_kinds = ['rect', 'ellipse', 'line', 'polyline', 'polygon', 'path', 'freehand', 'image']

fn shape_kind_ok(v) => v in shape_kinds

pub drawing_schema = {
  // ───────── drawing block (one inline embed in the flow doc) ─────────
  drawing: {
    role:     'block',
    content:  [{tag: 'layer', qty: 'plus'}],
    marks:    'none',
    atomic:   true,
    editable: true,
    attrs: [
      {name: 'id',     required: true,  type: 'string'},
      {name: 'width',  required: false, type: 'int',    default: 800},
      {name: 'height', required: false, type: 'int',    default: 600},
      {name: 'units',  required: false, type: 'symbol', default: 'px'},
      {name: 'grid',   required: false, type: 'int',    default: 10},
      {name: 'bg',     required: false, type: 'string', default: "#fff"}
    ]
  },

  // ───────── layer (z-stack inside one drawing) ─────────
  // NOTE: the JS reference carries a cosmetic `name` attr on layer/group. In the
  // Mark data model `name` is the reserved element-tag accessor (`name(el)` /
  // `el['name']` return the tag), so a `name` attribute cannot be represented or
  // validated on a Mark element. It is omitted here; the plain-map command layer
  // (mod_drawing_commands) may still carry an arbitrary `name` entry in `attrs`.
  layer: {
    role:    'drawing-container',
    content: [{role: 'drawing-object', qty: 'star'}],
    marks:   'none',
    attrs: [
      {name: 'id',      required: true,  type: 'string'},
      {name: 'visible', required: false, type: 'bool',   default: true},
      {name: 'locked',  required: false, type: 'bool',   default: false}
    ]
  },

  // ───────── shape (geometric primitives) ─────────
  shape: {
    role:       'drawing-object',
    content:    [],
    marks:      'none',
    atomic:     true,
    selectable: true,
    draggable:  true,
    attrs: [
      {name: 'id',           required: true,  type: 'string'},
      {name: 'kind',         required: true,  type: 'symbol', validate: shape_kind_ok},
      {name: 'x',            required: false, type: 'float'},
      {name: 'y',            required: false, type: 'float'},
      {name: 'width',        required: false, type: 'float'},
      {name: 'height',       required: false, type: 'float'},
      {name: 'rotate',       required: false, type: 'float',  default: 0.0},
      {name: 'points',       required: false, type: 'string'},
      {name: 'src',          required: false, type: 'string'},
      {name: 'fill',         required: false, type: 'string', default: "transparent"},
      {name: 'stroke',       required: false, type: 'string', default: "#000"},
      {name: 'stroke-width', required: false, type: 'float',  default: 1.0},
      {name: 'opacity',      required: false, type: 'float',  default: 1.0},
      {name: 'ports',        required: false, type: 'array'}
    ]
  },

  // ───────── connector (edge between two shapes or free points) ─────────
  connector: {
    role:       'drawing-object',
    content:    [{tag: 'label', qty: 'star'}],
    marks:      'none',
    atomic:     true,
    selectable: true,
    attrs: [
      {name: 'id',           required: true,  type: 'string'},
      {name: 'from-shape',   required: false, type: 'string'},
      {name: 'from-port',    required: false, type: 'string'},
      {name: 'from-x',       required: false, type: 'float'},
      {name: 'from-y',       required: false, type: 'float'},
      {name: 'to-shape',     required: false, type: 'string'},
      {name: 'to-port',      required: false, type: 'string'},
      {name: 'to-x',         required: false, type: 'float'},
      {name: 'to-y',         required: false, type: 'float'},
      {name: 'routing',      required: false, type: 'symbol', default: 'orthogonal'},
      {name: 'waypoints',    required: false, type: 'array',  default: []},
      {name: 'start-arrow',  required: false, type: 'symbol', default: 'none'},
      {name: 'end-arrow',    required: false, type: 'symbol', default: 'arrow'},
      {name: 'stroke',       required: false, type: 'string', default: "#000"},
      {name: 'stroke-width', required: false, type: 'float',  default: 1.0},
      {name: 'stroke-dash',  required: false, type: 'string', default: ""}
    ]
  },

  // ───────── group (sub-tree treated as one selectable unit) ─────────
  // (the JS `name` attr is omitted — see the layer note above re: Mark `name`)
  group: {
    role:       'drawing-object',
    content:    [{role: 'drawing-object', qty: 'plus'}],
    marks:      'none',
    selectable: true,
    draggable:  true,
    attrs: [
      {name: 'id', required: true, type: 'string'}
    ]
  },

  // ───────── text-frame (rich-text island in canvas-space) ─────────
  'text-frame': {
    role:       'drawing-object',
    content:    [{role: 'block', qty: 'plus'}],
    marks:      'none',
    selectable: true,
    draggable:  true,
    editable:   true,
    attrs: [
      {name: 'id',     required: true,  type: 'string'},
      {name: 'x',      required: true,  type: 'float'},
      {name: 'y',      required: true,  type: 'float'},
      {name: 'width',  required: true,  type: 'float'},
      {name: 'height', required: true,  type: 'float'},
      {name: 'rotate', required: false, type: 'float',  default: 0.0},
      {name: 'bg',     required: false, type: 'string', default: "transparent"}
    ]
  },

  // ───────── edge label ─────────
  label: {
    role:     'drawing-object',
    content:  [{role: 'inline', qty: 'star'}],
    marks:    'all',
    editable: true,
    attrs: [
      {name: 'offset', required: false, type: 'float', default: 0.5}
    ]
  }
}
