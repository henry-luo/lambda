// the demo and headless batch commands share the same checked file boundary.
import files: lambda.editor.raster.files
pub pn open_document(path) any^ { return files.open_document(path)^ }
pub pn place(st,path) any^ { return files.place(st,path)^ }
pub pn write_document(st,path,format) any^ { return files.write_document(st,path,format)^ }
pub pn read_action(path) any^ { return files.read_action(path)^ }
pub pn write_action(action,path) bool^ { return files.write_action(action,path)^ }
