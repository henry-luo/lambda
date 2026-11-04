// reuse Finder's filesystem snapshots and path rules (S12.4.1v3 / D10.2.1).
import browser: ~~.finder.mod_model
import filesystem: ~~.finder.mod_io
import attachments: lambda.chat.attachment

pub fn navigate(path, base = sys.proc.self.cwd#) map^ {
    let absolute = browser.absolute(path, base, sys.home#)
    let entries = filesystem.read_folder(absolute)^;
    {kind:"attach", folder:absolute, entries:entries, path:"", error:null}
}
pub fn rows(dialog) array^ => [for (e in dialog.entries where not starts_with(e.name, ".")^ order by not e.dir, lower(e.name)) e]
pub fn parent(path) any^ => browser.parent(path)
pub fn size_label(bytes) any^ => browser.size_label(bytes)
pub fn target(dialog, path) any^ => browser.absolute(path, dialog.folder, sys.home#)
pub fn entry(path) map^ {
    let absolute = browser.absolute(path, sys.proc.self.cwd#, sys.home#)
    let found = [for (e in filesystem.read_folder(browser.parent(absolute))^ where (e.path == absolute)^) e][0];
    if (found == null) raise error("Chat: file does not exist") else found
}
pub pn attach(path, scratch) map^ {
    let selected = entry(path)^
    if ((selected.dir or selected.size > attachments.MAX_SOURCE_BYTES)^) raise error("Chat: choose a file of at most 8 MiB")
    let payload = input(selected.path, 'binary')^;
    return attachments.zip_file(selected.name, payload, scratch)^
}
pub pn save(attachment, path) any^ {
    let checked = attachments.checked(attachment)^
    if ((trim(path) == "")^) raise error("Chat: choose a destination path")
    if (exists(path)) raise error("Chat: destination already exists; choose another name")
    // bytes are already encoded; a .zip destination must not request another archive build.
    output(attachments.bytes(checked)^, path, {format:'binary', atomic:true})^
    return path
}
