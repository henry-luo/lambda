import dom

view <media_owner_demo> state revision:0,source:"../media/test_video_audio.mp4",report:"",generation:null,source_changed:false {
    <html <body style:"margin:0;font-family:Arial;",
        <button id:"read",type:"button","Read volume">
        <button id:"quiet",type:"button","Set volume">
        <button id:"rerender",type:"button","Rerender parent">
        <button id:"replace",type:"button","Replace source">
        <output id:"volume",report> <output id:"revision",string(revision)>
        <output id:"source_changed",string(source_changed)>
        <video id:"player",src:source,controls:"",muted:"",width:320,height:160>
    >>
}
on click(evt) {
    let player=dom.query_selector(dom.document_element(evt.target),"#player");
    let key=dom.get_attribute(evt.target,"id");
    if (key=="quiet") {
        generation=dom.media_info(player).generation;
        dom.media_command(player,"volume",0.25)
    }
    else if (key=="read") {
        let info=dom.media_info(player);
        report=string(info.volume);
        source_changed=info.generation!=generation
    }
    else if (key=="rerender") { revision=revision+1 }
    else if (key=="replace") { source="../media/test_video.mp4" }
    'pass'
}
apply(<media_owner_demo>)
