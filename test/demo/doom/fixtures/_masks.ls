import dom
view <mask_probe> { <main id:"masks",
    <div id:"ring", class:"box", style:"left:20px;mask-image:radial-gradient(circle,transparent 60%,black 63%);">
    <div class:"box", style:"left:120px;mask-image:radial-gradient(circle closest-side,black 50%,transparent 70%);",
        <div style:"width:80px;height:80px;background:blue;">
    >
    <div class:"box", style:"left:240px;transform:scale(1.5);mask-image:radial-gradient(circle,transparent 60%,black 63%);">
    <div class:"box", style:"left:330px;mask-image:radial-gradient(circle,black 0 24px,transparent 26px);">
    <button id:"clear", style:"position:absolute;left:20px;top:130px;", "Remove mask">
> }
on click(evt) {
    let owner = dom.closest(evt.target,"#masks")
    dom.style_set_property(dom.get_element_by_id(owner,"ring"),"mask-image","none")
    return 'handled'
}
<html <head <style "html,body{margin:0;background:white}.box{position:absolute;top:20px;width:80px;height:80px;background:red}">>
    <body apply(<mask_probe>)>>
