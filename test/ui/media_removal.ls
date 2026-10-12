import dom
view <media_removal_controls> state saved:null,report:"" {
    <section
        <button id:"play",type:"button","Play">
        <button id:"remove",type:"button","Remove">
        <button id:"read",type:"button","Read retained player">
        <output id:"state",report>
    >
}
on click(evt) {
    let root=dom.document_element(evt.target);
    let key=dom.get_attribute(evt.target,"id");
    let player=dom.query_selector(root,"#player");
    if (key=="play") {
        saved=player;
        dom.media_command(player,"play",0);
        dom.request_frame(evt.currentTarget,"read_player")
    } else if (key=="remove") {
        dom.remove_child(dom.parent_node(player),player)
    } else if (key=="read") {
        report=dom.media_info(saved).state
    }
    'pass'
}
on read_player(evt) {
    report=dom.media_info(saved).state;
    if (report=="loading" or report=="ready") { dom.request_frame(evt.currentTarget,"read_player") }
}
let controls=<media_removal_controls>;
<html <body style:"margin:0;font-family:Arial;",
    apply(controls);
    <video id:"player",src:"../media/test_video_audio.mp4",muted:"",width:320,height:160>
>>
