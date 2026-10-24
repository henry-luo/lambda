// Retained presentation branches keep their DOM identity and live control values (D4.5.3).
import dom
view <projection_counter> state count: 0 {
    <section id:"counter", ["data-count"]:string(count),
        <div id:"stable", <input id:"draft",value:"seed"> <span id:"probe","unchanged">>
        <button id:"increment","Increment">
        <button id:"scroll-frame","Scroll on frame">
        <span id:"count",string(count)>
        <input id:"controlled",value:string(count)>
        <div id:"metadata", ["data-revision"]:string(count), ["aria-label"]:string(count), "metadata">
        <div id:"frame-scrollport",style:"height:64px;overflow:auto",
            <div style:"height:400px"> <div id:"frame-scroll-target",style:"height:16px","end">>
        if (count>1) <p id:"extra","added"> else null
    >
}
on click(evt) {
    if (dom.get_attribute(evt.target,"id")=="increment") {
        count = count+1
    }
    if (dom.get_attribute(evt.target,"id")=="scroll-frame") dom.request_frame(evt.target,"scroll_ready")
}
on scroll_ready(evt) {
    // a frame's scroll request has no DOM mutation to drive its commit.
    dom.scroll_into_view_options(dom.get_element_by_id(dom.root_node(evt.target),"frame-scroll-target"),{block:"end"})
    return 'pass'
}
// one event dirties both instances; only the parent's final projection owns the child.
view <nested_child> state clicks: 0 {
    <button id:~.id, string(~.revision) ++ ":" ++ string(clicks)>
}
on click() {
    clicks = clicks+1
    emit("increment_parent",{})
}
on render(evt) {
    if (evt.event_phase==2) dom.set_attribute(evt.target,"data-observed-revision",string(~.revision))
    return 'pass'
}
let fixed_child = <nested_child id:"fixed-increment",revision:0>
view <nested_parent> state revision: 0 {
    <section id:if (~.fixed) "fixed-parent" else "nested-parent",
        <span id:if (~.fixed) "fixed-revision" else "nested-revision",string(revision)>
        apply(if (~.fixed) fixed_child else <nested_child id:"nested-increment",revision:revision>)>
}
on increment_parent() { revision = revision+1 }
<html
    <head <style "#counter[data-count='1'] #probe {color:rgb(12,34,56);}">>
    <body apply(<projection_counter>); apply(<nested_parent>); apply(<nested_parent fixed:true>)>
>
