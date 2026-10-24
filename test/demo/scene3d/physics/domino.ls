import scenes: ._scenes
import ui: ._ui

ui.page("domino",scenes.domino(),{
    title:"Domino circuit",
    description:"A winding line of 32 tiles turns a small impulse into a travelling chain reaction. Start at one end, then counter-nudge the middle to make two fronts meet.",
    actions:[{id:"launch",label:"Start cascade",primary:true,restart:true,play:true,
        request:{operation:"apply",commands:scenes.domino_commands()}},
      {id:"nudge",label:"Counter-nudge middle",play:true,request:{operation:"apply",commands:[
        {type:"impulse",body:"tile-16",value:[-0.3*scenes.domino_pace,0.0,0.14*scenes.domino_pace],
         point:[scenes.domino_position(16)[0],1.58,scenes.domino_position(16)[2]]}]}}],
    labels:["TILES DOWN / 32","CONTACT STARTS","FASTEST TILE"],
    note:"Every tile is a separate rigid body. A point impulse tips the first tile; the remaining motion comes from collisions and gravity."
})
