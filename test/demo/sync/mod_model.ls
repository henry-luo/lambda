// View state contains public progress only; keys and credential handles stay in the worker.
pub fn initial(folder) => {mode:"loading",user:"",input_code:"",pair_code:null,folder:folder,
    status:null,note:null,linked:false}
pub fn update(st, event) => if (event.type == "loaded") {*:st,mode:"setup"}
    else if (event.type == "pairing") {*:st,mode:"pairing",user:event.user,pair_code:event.code,note:null}
    else if (event.type == "joining") {*:st,mode:"joining",user:event.user,pair_code:null,note:null}
    else if (event.type == "paired") {*:st,mode:"starting",pair_code:null,input_code:"",linked:true,note:null}
    else if (event.type == "running") {*:st,mode:"active",user:event.user,folder:event.folder,pair_code:null,note:null}
    else if (event.type == "status") {*:st,status:event.status}
    else if (event.type == "error") {*:st,mode:"error",pair_code:null,input_code:"",note:event.message}
    else st
pub fn title(st) => if (st.mode == "loading") "Opening your sync folder"
    else if (st.mode == "pairing") "Link your other device"
    else if (st.mode == "join") "Link this device"
    else if (st.mode == "joining") "Looking for your other device"
    else if (st.mode == "starting") "Device linked"
    else if (st.mode == "paused") "Sync paused"
    else if (st.mode == "error") "Sync needs your attention"
    else if (st.mode == "active" and st.status.connected == true) "Syncing your files"
    else if (st.mode == "active" and st.status.sessions > 0) "Watching for changes"
    else if (st.mode == "active") "Looking for your devices"
    else "Your files, together"
pub fn detail(st) any^ => if (st.mode == "active" and st.status.discovery_error != null)
        "Local discovery could not start. Check network permissions, then resume sync."
    else if (st.mode == "active" and st.status.fallback_reason != null)
        "A connection did not complete. Retrying automatically."
    else if (st.mode == "active" and st.status.progress.review == true)
        "An external edit needs review. Your local files have been preserved."
    else if (st.mode == "active" and len(st.status.progress.conflicts or []) > 0)
        "Some files have concurrent changes. Their versions are preserved for review."
    else if (st.mode == "active" and st.status.sessions > 0)
        "Changes in this folder sync automatically while your devices are on the same network."
    else if (st.mode == "active") "Keep Lambda Sync open on your paired devices on the same network."
    else if (st.mode == "joining") "Keep the pairing screen open on the first device."
    else if (st.mode == "paused") "Your files stay available. Resume when you are ready."
    else "Use the same user ID on your devices. A one-time code links them."
