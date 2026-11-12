import model: .mod_model
fn button(action, text, primary=false) => <button type:"button",'data-action':action,
    class:if (primary) "primary" else "secondary",text>
fn mark() => <svg class:"sync-mark",viewBox:"0 0 48 48",'aria-hidden':"true",
    <path d:"M10 20a15 15 0 0 1 26-7l4 4 M40 8v9h-9 M38 28a15 15 0 0 1-26 7l-4-4 M8 40v-9h9",
        fill:"none",stroke:"currentColor",'stroke-width':"3.5",'stroke-linecap':"round",'stroke-linejoin':"round">>
fn field(label, id, value, placeholder) => <label class:"field",<span label>
    <input id:id,type:"text",value:value,placeholder:placeholder,autocomplete:"off",spellcheck:"false">>
pub fn page(st) => <main id:"sync-demo",'data-mode':st.mode,
    <header <div class:"brand",(mark());<span "Lambda Sync">><span class:"badge","LAN demo">>
    <section class:"card",
        <div class:"hero",(mark())>
        <h1 model.title(st)><p class:"description",model.detail(st)>;
        if (st.mode in ["setup","join"]) <div class:"setup",
            (field("User ID","user-id",st.user,"e.g. henry"));
            if (st.mode == "join") field("One-time pairing code","pair-code",st.input_code,"xxxx-xxxx-xxxx-xxxx-xxxx") else null;
            <div class:"actions",
                if (st.mode == "setup") [button("create","Create sync",true),button("show-join","Link this device")]
                else [button("join","Link device",true),button("back","Back")]>>
        else if (st.mode == "pairing") <div class:"pairing",
            <p "On the other device, run ";<strong "lambda demo sync">;" and choose Link this device.">
            <div class:"identity",<span "User ID"><strong st.user>>
            <div id:"pairing-code",st.pair_code>
            <p class:"caption","Enter this code there. It expires in two minutes and links one device.">
            <div class:"actions",(button("copy-code","Copy code"));(button("resume","Done for now"))>>
        else if (st.mode == "active" or st.mode == "paused") <div class:"connected",
            <div class:"identity",<span "User ID"><strong st.user>>
            <div class:"actions",(button("folder","Open folder",true));
                if (st.mode == "active") button("pause","Pause") else button("resume","Resume");
                (button("pair","Link another device"))>>
        else if (st.mode == "error") <div class:"actions",(button("resume","Resume sync",true));(button("setup","Back to setup"))>
        else <div class:"waiting",<span class:"pulse"><span "Please wait…">;(button("cancel","Cancel"))>;
        if (st.note != null) <p id:"sync-error",role:"alert",st.note> else null;
        <footer <span "Sync folder"><code id:"sync-folder",st.folder>>>
    <p class:"footnote","Files stay on your devices. This demo connects over your local network.">>
