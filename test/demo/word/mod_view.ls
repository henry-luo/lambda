// View choices are session presentation; they never edit document or history (D7.2.5).
pub let modes = [{id:"read",command:"view.readMode",label:"Read Mode",icon:"book",ribbon:"view-reading"},
    {id:"print",command:"view.printLayout",label:"Print Layout",icon:"cover",ribbon:"view-print"},
    {id:"web",command:"view.webLayout",label:"Web Layout",icon:"globe",ribbon:"view-web"}]
pub fn mode(command) => [for (choice in modes where choice.command==command) choice][0]
pub fn active(layout,read_only) => if (read_only) "read" else layout
pub fn activate(st,command,enabled=null) {
    let choice = mode(command)
    if (choice==null) st else {
        // the ribbon can toggle reading; status buttons always select their named view.
        let reading = choice.id=="read" and (enabled or (enabled==null and not st.read_only))
        let layout = if (choice.id=="read") st.layout else choice.id;
        {*:st,layout:layout,read_only:reading,
            status:if (reading) "Read mode" else if (layout=="web") "Web layout" else "Ready to write"}
    }
}
