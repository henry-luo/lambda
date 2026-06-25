// Combined doc schema = html5 subset + drawing entries.
// This is the default schema used when an editor is loaded with mixed
// (flow + drawing) content.

import { drawingSchemaEntries } from '../drawing/schema.js'
import { html5SubsetEntries } from './html5.js'
import type { Schema } from '../model/schema.js'

export const docSchemaEntries = {
  ...html5SubsetEntries,
  ...drawingSchemaEntries
}

export const docSchema: Schema = {
  entries: docSchemaEntries,
  default_block: 'p'
}
