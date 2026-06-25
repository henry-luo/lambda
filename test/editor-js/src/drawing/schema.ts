// Drawing schema entries. Per Stage-4 design doc §3.1.
//
// Add these to the base schema to get a doc model that supports inline
// drawings. Each entry follows the same shape as the html5 schema entries.

import type { SchemaEntry } from '../model/schema.js'

const SHAPE_KINDS = ['rect', 'ellipse', 'line', 'polyline', 'polygon', 'path', 'freehand', 'image'] as const

export const drawingSchemaEntries: Record<string, SchemaEntry> = {
  drawing: {
    role: 'block',
    content: [{ tag: 'layer', qty: 'plus' }],
    marks: 'none',
    atomic: true,
    editable: true,
    attrs: [
      { name: 'id',     required: true,  type: 'string' },
      { name: 'width',  required: false, type: 'int',    default: 800 },
      { name: 'height', required: false, type: 'int',    default: 600 },
      { name: 'units',  required: false, type: 'symbol', default: 'px' },
      { name: 'grid',   required: false, type: 'int',    default: 10 },
      { name: 'bg',     required: false, type: 'string', default: '#fff' }
    ]
  },

  layer: {
    role: 'drawing-container',
    content: [{ role: 'drawing-object', qty: 'star' }],
    marks: 'none',
    attrs: [
      { name: 'id',      required: true,  type: 'string' },
      { name: 'name',    required: false, type: 'string', default: '' },
      { name: 'visible', required: false, type: 'bool',   default: true },
      { name: 'locked',  required: false, type: 'bool',   default: false }
    ]
  },

  shape: {
    role: 'drawing-object',
    content: [],
    marks: 'none',
    atomic: true,
    selectable: true,
    draggable: true,
    attrs: [
      { name: 'id',     required: true, type: 'string' },
      { name: 'kind',   required: true, type: 'string',
        validate: v => typeof v === 'string' && (SHAPE_KINDS as readonly string[]).indexOf(v) >= 0 },
      // geometry — interpretation depends on kind
      { name: 'x',      required: false, type: 'float' },
      { name: 'y',      required: false, type: 'float' },
      { name: 'width',  required: false, type: 'float' },
      { name: 'height', required: false, type: 'float' },
      { name: 'rotate', required: false, type: 'float', default: 0 },
      { name: 'points', required: false, type: 'string' },
      { name: 'src',    required: false, type: 'string' },
      // style
      { name: 'fill',         required: false, type: 'string', default: 'transparent' },
      { name: 'stroke',       required: false, type: 'string', default: '#000' },
      { name: 'stroke-width', required: false, type: 'float',  default: 1 },
      { name: 'opacity',      required: false, type: 'float',  default: 1 },
      // ports (anchor points for connectors)
      { name: 'ports',  required: false, type: 'array' }
    ]
  },

  connector: {
    role: 'drawing-object',
    content: [{ tag: 'label', qty: 'star' }],
    marks: 'none',
    atomic: true,
    selectable: true,
    attrs: [
      { name: 'id',            required: true,  type: 'string' },
      { name: 'from-shape',    required: false, type: 'string' },
      { name: 'from-port',     required: false, type: 'string' },
      { name: 'from-x',        required: false, type: 'float' },
      { name: 'from-y',        required: false, type: 'float' },
      { name: 'to-shape',      required: false, type: 'string' },
      { name: 'to-port',       required: false, type: 'string' },
      { name: 'to-x',          required: false, type: 'float' },
      { name: 'to-y',          required: false, type: 'float' },
      { name: 'routing',       required: false, type: 'string', default: 'orthogonal' },
      { name: 'waypoints',     required: false, type: 'array',  default: [] },
      { name: 'start-arrow',   required: false, type: 'string', default: 'none' },
      { name: 'end-arrow',     required: false, type: 'string', default: 'arrow' },
      { name: 'stroke',        required: false, type: 'string', default: '#000' },
      { name: 'stroke-width',  required: false, type: 'float',  default: 1 },
      { name: 'stroke-dash',   required: false, type: 'string', default: '' }
    ]
  },

  group: {
    role: 'drawing-object',
    content: [{ role: 'drawing-object', qty: 'plus' }],
    marks: 'none',
    selectable: true,
    draggable: true,
    attrs: [
      { name: 'id',   required: true,  type: 'string' },
      { name: 'name', required: false, type: 'string', default: '' }
    ]
  },

  'text-frame': {
    role: 'drawing-object',
    content: [{ role: 'block', qty: 'plus' }],
    marks: 'none',
    selectable: true,
    draggable: true,
    editable: true,
    attrs: [
      { name: 'id',     required: true,  type: 'string' },
      { name: 'x',      required: true,  type: 'float' },
      { name: 'y',      required: true,  type: 'float' },
      { name: 'width',  required: true,  type: 'float' },
      { name: 'height', required: true,  type: 'float' },
      { name: 'rotate', required: false, type: 'float', default: 0 },
      { name: 'bg',     required: false, type: 'string', default: 'transparent' }
    ]
  },

  label: {
    role: 'drawing-object',
    content: [{ role: 'inline', qty: 'star' }],
    marks: 'all',
    editable: true,
    attrs: [
      { name: 'offset', required: false, type: 'float', default: 0.5 }
    ]
  }
}

export const SHAPE_KIND_LIST = SHAPE_KINDS
