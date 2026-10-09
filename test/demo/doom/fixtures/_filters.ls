import dom
let TREE = <main id:"filter-probe",
    <svg width:"0", height:"0", 'aria-hidden':"true",
        <defs
            <filter id:"half-green", 'color-interpolation-filters':"sRGB",
                <feColorMatrix type:"matrix", values:"0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 .5 0">
            >
            <filter id:"shift", x:"-50%", y:"-50%", width:"200%", height:"200%",
                <feOffset dx:"10", dy:"0">
            >
        >
    >
    <div id:"matrix", class:"box", style:"filter:url(#half-green);">
    <div id:"chain", class:"box", style:"left:100px;filter:url(#half-green) brightness(.5);">
    <div id:"shifted", class:"box", style:"left:180px;filter:url(#shift);">
    <div id:"alpha", class:"box", style:"left:260px;background:rgba(255,0,0,.5);filter:brightness(1);">
    <div id:"scaled", class:"box", style:"left:340px;transform:scale(2);filter:url(#half-green);opacity:.5;">
    <div id:"tilted", class:"box", style:"left:440px;transform:perspective(150px) rotateY(35deg);filter:url(#half-green);">
    <button id:"cancel", style:"position:absolute;top:100px;", "Cancel filter">
>
view <filter_probe> { ~.rendered }
on click(evt) {
    let owner = dom.closest(evt.target, "#filter-probe")
    dom.presentation_style_set_property(dom.get_element_by_id(owner, "matrix"), "filter", "none")
    return 'handled'
}
<html <head <title "HTML CSS SVG filter references and ordered alpha processing">
    <style "html,body{margin:0;background:rgb(50,100,150)}.box{position:absolute;left:20px;top:20px;width:40px;height:40px;background:red}">>
    <body apply(<filter_probe rendered:TREE>)>>
