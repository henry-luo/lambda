import ui: lambda.ui.blade
import dom
import resources: ~~.demo.mod_resources

let sound_base = url_resolve(resources.entry_uri()^,".")
let click_sound = input(url_resolve(sound_base,"assets/blade_click.wav"),'binary')^
let glove_cursor = url_resolve(sound_base,"assets/blade_gauntlet.svg")
let empty_slot = {icon:null,label:"Empty slot"}

let inventory = [
    {icon:"sword",label:"Iron Oath",rarity:'legendary'},{icon:"bow",label:"Ranger's longbow"},
    {icon:"helmet",label:"Warden's helm",rarity:'rare'},{icon:"potion-red",label:"Crimson elixir",quantity:8},
    {icon:"potion-blue",label:"Mana draught",quantity:5},{icon:"potion-green",label:"Wildroot tonic",quantity:3},
    {icon:"armor",label:"Sentinel plate",rarity:'rare'},{icon:"boots",label:"Wayfarer's boots"},
    {icon:"scroll",label:"Scroll of recall",rarity:'epic'},{icon:"shield",label:"Dawnkeeper",rarity:'legendary'},
    {icon:"ring",label:"Azure signet",rarity:'epic'},{icon:"gem",label:"Amethyst shard",rarity:'epic',quantity:12},
    {icon:"hammer",label:"Stormbreaker",rarity:'uncommon'},{icon:"ice",label:"Frozen heart",rarity:'rare'},
    {icon:"heal",label:"Sun talisman",rarity:'uncommon'},
    empty_slot,empty_slot,empty_slot,empty_slot,empty_slot]
let abilities = [
    {icon:"fire",label:"Fireball",hotkey:"1"},{icon:"ice",label:"Frostbolt",hotkey:"2"},
    {icon:"heal",label:"Heal",hotkey:"3"},{icon:"hammer",label:"Stun",hotkey:"4"},
    {icon:"gem",label:"Arcane",hotkey:"Q"},{icon:"dash",label:"Dash",hotkey:"W"},
    {icon:"shield",label:"Defend",hotkey:"E"},{icon:"skull",label:"Execute",hotkey:"R"}]
let starting_equipment = [
    {place:"head",label:"Head",icon:"helmet",name:"Ashen crown",rarity:'legendary'},
    {place:"chest",label:"Chest",icon:"armor",name:"Ember cuirass",rarity:'rare'},
    {place:"hands",label:"Hands",icon:"ring",name:"Oathbound ring",rarity:'epic'},
    {place:"legs",label:"Legs",icon:"boots",name:"Marcher's greaves",rarity:'common'},
    {place:"weapon",label:"Weapon",icon:"sword",name:"Dawn blade",rarity:'legendary'},
    {place:"shield",label:"Shield",icon:"shield",name:"Sunward",rarity:'epic'}]
fn gear_place(icon) => if (icon == "helmet") "head" else if (icon == "armor") "chest"
    else if (icon == "ring") "hands" else if (icon == "boots") "legs"
    else if (contains(["sword","bow","hammer"],icon)) "weapon"
    else if (icon == "shield") "shield" else null
fn spent_potion(entry) => if (entry.quantity > 1) {*:entry,quantity:entry.quantity - 1}
    else empty_slot
fn replace_at(values,index,replacement) =>
    [for (i,value in values) if (i == index) replacement else value]
let silhouette = <svg viewBox:"0 0 220 400",style:"width:100%;height:390px;",["aria-hidden"]:"true",
    <path d:"M110 35 C85 35 85 74 97 85 L92 104 L64 117 Q52 126 48 151 L30 225 L42 230 L72 169 L79 214 L72 274 L83 349 L75 377 Q92 389 100 374 L108 304 L114 304 L122 374 Q132 388 148 377 L140 349 L150 274 L143 214 L151 169 L180 230 L192 225 L173 151 Q169 126 157 117 L128 104 L123 85 C137 74 136 35 110 35 Z",
        fill:"#947953",stroke:"#725b3e",["stroke-width"]:1.5,opacity:0.4>>
let gallery_css = "
.blade-gallery{max-width:1280px;margin:auto}.blade-masthead{display:flex;align-items:center;justify-content:space-between;gap:20px;margin-bottom:30px}.blade-wordmark{display:flex;gap:14px;align-items:center}.blade-wordmark h1{font-size:30px;line-height:1.1;margin:0;color:var(--blade-gold-light);letter-spacing:0.14em}.blade-eyebrow{font-size:10px;letter-spacing:0.25em;color:var(--blade-text-secondary);text-transform:uppercase;margin:8px 0 0}.blade-gallery-grid{display:grid;grid-template-columns:342px minmax(0,1fr) 300px;gap:24px;align-items:start}.blade-inventory-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;justify-items:center}.blade-capacity{font-size:13px;text-align:center;color:var(--blade-text-secondary);margin:0 0 12px}.blade-character{padding:4px 8px;text-align:center}.blade-character h2{font-size:31px;color:var(--blade-gold-light);margin:24px 0 4px;letter-spacing:0.04em}.blade-character-subtitle{font-size:12px;color:var(--blade-text-secondary);letter-spacing:0.15em;text-transform:uppercase;margin-bottom:20px}.blade-stat-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:12px;margin:16px 0 24px;padding:18px 0;border-top:1px solid var(--blade-border);border-bottom:1px solid var(--blade-border)}.blade-stat-grid dt{font-size:10px;letter-spacing:0.12em;color:var(--blade-text-secondary)}.blade-stat-grid dd{font-size:25px;color:var(--blade-gold-light);margin:4px 0 0}.blade-selection{padding:16px;border:1px solid var(--blade-border);border-radius:5px;background:#00000033;text-align:left}.blade-selection-name{font-size:18px;color:var(--blade-gold-light);margin:0 0 4px}.blade-selection-description{font-size:12px;line-height:1.7;color:var(--blade-text-secondary);margin:0}.blade-equipment{position:relative;height:410px}.blade-equipment>svg{position:absolute;inset:8px 0 0}.blade-equipped{position:absolute;text-align:center}.blade-equipped-label{display:block;font-size:12px;margin-bottom:5px}.blade-equipped-head{left:88px;top:8px}.blade-equipped-chest{left:88px;top:104px}.blade-equipped-hands{left:0;top:186px}.blade-equipped-legs{right:0;top:186px}.blade-equipped-weapon{left:0;top:292px}.blade-equipped-shield{right:0;top:292px}.blade-ability-bar{grid-column:1 / -1}.blade-abilities{display:grid;grid-template-columns:repeat(8,1fr);gap:20px;justify-items:center}.blade-ability-cell{text-align:center}.blade-ability-cell>.blade-ability{margin:auto}.blade-ability-label{display:block;margin-top:12px;font-size:13px;color:var(--blade-gold-light)}.blade-footer{display:flex;justify-content:space-between;gap:20px;flex-wrap:wrap;margin-top:24px;color:var(--blade-text-secondary);font-size:12px}.blade-footer a{color:var(--blade-text-secondary)}
@media(max-width:1100px){.blade-gallery-grid{grid-template-columns:342px minmax(0,1fr)}.blade-equipment-panel{grid-column:2;grid-row:2}.blade-character{grid-column:2;grid-row:1}.blade-ability-bar{grid-column:1 / -1}.blade-abilities{grid-template-columns:repeat(4,1fr)}}
@media(max-width:400px){.blade-inventory-grid,.blade-abilities{grid-template-columns:repeat(3,1fr)}}
@media(max-width:700px){.blade-masthead{align-items:flex-start;flex-direction:column}.blade-gallery-grid{grid-template-columns:minmax(0,1fr)}.blade-character,.blade-equipment-panel,.blade-ability-bar{grid-column:1;grid-row:auto}.blade-inventory-grid{grid-template-columns:repeat(4,1fr)}.blade-equipment{max-width:244px;margin:auto}.blade-abilities{gap:18px 8px}.blade-stat-grid{gap:6px}.blade-wordmark h1{font-size:25px}}
.blade-gallery .blade-meter-fill{transition:width .55s ease-out}.blade-gallery .blade-meter-mana .blade-meter-fill{box-shadow:inset 0 1px 2px #ffffffaa,0 0 20px #5fd4ff}
" ++ ".blade-gallery,.blade-gallery *{cursor:url('" ++ glove_cursor ++ "') 3 3,pointer!important}"

view blade_adventurer: <blade_adventurer> state selected:0, selected_label:"Iron Oath", bag:inventory,
    equipment:starting_equipment, health:840, mana:360, last_action:"Your adventure awaits.",
    sound_token:null {
    let occupied = len([for (entry in bag where entry.icon != null) entry]);
    <main id:"blade-gallery",class:"blade-gallery",
        <style gallery_css>
        <header class:"blade-masthead",*[
            <div class:"blade-wordmark",*[ui.render(<blade.rpg_icon name:'shield',size:52>)^,
                <div <h1 "BLADE & EMBER"> <p class:"blade-eyebrow","Character menu · High fantasy UI">>]>,
            ui.render(<blade.space gap:12,<blade.tag "LEVEL 28"> <blade.button id:"blade-journal",variant:'primary',href:"blade_components.ls","Open journal">>)^]>
        <div class:"blade-gallery-grid",*[
            ui.render(<blade.panel id:"blade-inventory",title:"Inventory",
                <p class:"blade-capacity","Slots: " ++ string(occupied) ++ " / " ++ string(len(bag))>
                <div style:"margin-bottom:18px;",ui.render(<blade.progress percent:occupied*100/len(bag),show_info:false,stroke_width:4>)^>
                <div class:"blade-inventory-grid",*[for (index,entry in bag)
                    ui.render(<blade.item_slot *:entry,id:"blade-item-" ++ string(index),size:64,selected:index == selected>)^]>
                <p style:"font-size:12px;color:var(--blade-text-secondary);text-align:center;margin:18px 0 0;","Select gear to equip it. Potions restore vitality or arcane power.">>)^,
            <section class:"blade-character",*[
                ui.render(<blade.meter id:"blade-health",resource:'health',label:"VITALITY",value:health,max:1000>)^,
                <h2 "Aelric of Ashenvale">,
                <div class:"blade-character-subtitle","Dawnwarden · Order of the Silver Flame">,
                ui.render(<blade.meter id:"blade-mana",resource:'mana',label:"ARCANE POWER",value:mana,max:500>)^,
                <dl class:"blade-stat-grid",*[for (key,value in {STR:45,DEX:30,INT:50,VIT:40}) <div <dt string(key)> <dd string(value)>>]>,
                <div class:"blade-selection",<p id:"blade-selection",class:"blade-selection-name",selected_label>
                    <p class:"blade-selection-description","Choose inventory gear to equip it; choose equipped gear to return it to the bag. Drink potions to restore vitality or arcane power.">>,
                <output id:"blade-last-action",style:"display:block;font-size:12px;margin-top:18px;color:var(--blade-text-secondary);",last_action>]>,
            ui.render(<blade.panel class:"blade-equipment-panel",title:"Equipment",material:'parchment',
                <p style:"text-align:center;margin:0 0 8px;font-size:13px;","Character attributes">
                <div class:"blade-equipment",*[silhouette,*[for (entry in equipment)
                    <div class:"blade-equipped blade-equipped-" ++ entry.place,
                        <span class:"blade-equipped-label",entry.label>
                        ui.render(<blade.item_slot id:"blade-equipped-" ++ entry.place,icon:entry.icon,rarity:entry.rarity,label:entry.name,size:64>)^>]]>>) ^,
            ui.render(<blade.panel title:"Abilities",class:"blade-ability-bar",
                <div class:"blade-abilities",*[for (entry in abilities) <div class:"blade-ability-cell",*[
                    ui.render(<blade.ability *:entry,id:"blade-cast-" ++ entry.icon,size:70>)^,
                    <span class:"blade-ability-label",entry.label>]>]>>) ^]>
        <footer class:"blade-footer",<span "λ · lambda.ui.blade · Forged for your next adventure.">
            <a href:"https://github.com/RonenNess/RPGUI","Inspired by RPGUI ↗">>>
}
on ui_action(action) {
    if (action.component == "item-slot" and starts_with(action.id,"blade-item-")) {
        let index = int(slice(action.id,len("blade-item-")))
        let item = bag[index]
        selected = index
        selected_label = item.label
        if (starts_with(item.icon or "","potion-")) {
            let restored = if (item.icon == "potion-red") 0
                else min(if (item.icon == "potion-blue") 120 else 60,500 - mana)
            let healed = if (item.icon == "potion-blue") 0 else min(40,1000 - health)
            if (restored == 0 and healed == 0) {
                last_action = if (item.icon == "potion-red") "Your vitality is already full."
                    else if (item.icon == "potion-blue") "Your arcane power is already full."
                    else "Your reserves are already full."
            }
            else {
                // consume only the amount that can actually restore a resource.
                mana = mana + restored
                health = health + healed
                bag = replace_at(bag,index,spent_potion(item))
                let result = if (healed > 0 and restored > 0)
                    "Vitality +" ++ string(healed) ++ ", arcane power +" ++ string(restored)
                    else if (healed > 0) "Vitality +" ++ string(healed)
                    else "Arcane power +" ++ string(restored)
                last_action = "Drank " ++ item.label ++ ". " ++ result ++ "."
            }
        } else {
            let place = gear_place(item.icon)
            if (place == null) { last_action = "Inspecting " ++ item.label ++ "." }
            else {
                let slot = index_of([for (entry in equipment) entry.place],place)
                let previous = equipment[slot]
                // swap the displaced gear into the same bag slot so no item is duplicated.
                equipment = replace_at(equipment,slot,
                    {*:previous,icon:item.icon,name:item.label,rarity:item.rarity})
                bag = replace_at(bag,index,
                    {icon:previous.icon,label:previous.name,rarity:previous.rarity})
                last_action = "Equipped " ++ item.label ++ "."
            }
        }
    } else if (action.component == "item-slot" and starts_with(action.id,"blade-equipped-")) {
        let place = slice(action.id,len("blade-equipped-"))
        let slot = index_of([for (entry in equipment) entry.place],place)
        let item = equipment[slot]
        let empty = [for (i,entry in bag where entry.icon == null) i]
        if (item.icon == null) { last_action = "That equipment slot is empty." }
        else if (len(empty) == 0) { last_action = "Your bag is full." }
        else {
            let destination = empty[0]
            bag = replace_at(bag,destination,
                {icon:item.icon,label:item.name,rarity:item.rarity})
            equipment = replace_at(equipment,slot,
                {*:item,icon:null,name:"Empty slot",rarity:'common'})
            selected = destination
            selected_label = item.name
            last_action = "Returned " ++ item.name ++ " to inventory."
        }
    } else if (action.id == "blade-cast-heal") {
        health = min(1000,health + 80)
        last_action = "Healing light restores 80 vitality."
    } else if (action.component == "ability") { last_action = "Casting " ++ string(action.value) ++ "." }
}
on click(evt) {
    let owner = dom.closest(evt.target,"#blade-gallery")
    if (owner == null) { return 'pass' }
    if (sound_token == null) { sound_token = dom.audio_open(owner,click_sound) }
    if (sound_token != null) dom.audio_play(owner,sound_token,0.35)
    return 'pass'
}
apply(<blade.page title:"Blade & Ember · Lambda UI",<blade_adventurer>>)
