import dom
import menu: lambda.ui.core.menu

view <menu_test> () any^ {
    <div id:"menu-host",style:"position:relative;margin:60px;width:500px;height:230px;",
        menu.frame("menu-one","menu-host",<div id:"menu-one",["data-menu"]:"menu-one",role:"menu",style:"position:fixed;left:0;top:0;width:140px;height:90px;",
            <button id:"first-item",["data-menu-item"]:"",type:"button","First">;
            <button id:"disabled-item",["data-menu-item"]:"",disabled:"",type:"button","Disabled">;
            <button id:"last-item",["data-menu-item"]:"",type:"button","Last">>,false,"[data-menu-item]:not([disabled])");
        menu.frame("menu-two","menu-host",<div id:"menu-two",["data-menu"]:"menu-two",role:"menu",style:"position:fixed;left:290px;top:140px;width:140px;height:90px;",
            <button id:"other-first",["data-menu-item"]:"",type:"button","Other first">;
            <button id:"other-last",["data-menu-item"]:"",type:"button","Other last">>,false,"[data-menu-item]:not([disabled])")>
}
on keydown(evt) {
    let current = dom.closest(evt.target,"[data-menu]")
    if (current!=null and evt.key in ["ArrowUp","ArrowDown","Home","End"]) {
        menu.navigate(evt.target,dom.get_attribute(current,"id"),evt.key,"[data-menu-item]:not([disabled])")^
        return 'prevent-default'
    }
    return 'pass'
}

<html <body apply(<menu_test>)>>
