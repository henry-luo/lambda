// The demo constructs its default page once. Embedded hosts import the editor module.
import editor: .mod_editor

pub let DEFAULT_DECK = editor.DEFAULT_DECK
pub fn document(deck) => editor.document(deck)

document(DEFAULT_DECK)
