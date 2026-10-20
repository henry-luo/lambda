// D7.2.4v3: headless value facade; native mounting is an explicit view module.
import core: .raster.core
import model: .raster.model
import history: .mod_history
import registry: .raster.commands
pub fn new_document(options={}) any^ => core.new_document(options)^
pub fn open_session(image) any^ => core.open_session(image)^
pub fn exec(session,command) any^ => core.exec(session,command)^
pub fn layers(image) => model.layers(image)
pub fn lookup(image,id) => model.lookup(image,id)
pub fn validate(image) any^ => model.validate(image)^
pub fn dirty(session) => core.dirty(session)
pub fn mark_saved(session,snapshot) => core.mark_saved(session,snapshot)
pub fn can_undo(session) bool^ => history.can_undo(session.hist)^
pub fn can_redo(session) bool^ => history.can_redo(session.hist)^
pub fn commands() => registry.catalog
pub fn command_state(session,id) any^ => registry.command_state(session,id)^
