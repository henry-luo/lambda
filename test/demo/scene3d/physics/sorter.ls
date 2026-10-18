import scenes: ._scenes
import ui: ._ui

ui.page("sorter",scenes.sorter(),{
    title:"Marble sorter",
    description:"Eighteen marbles roll down an inclined tray. A motor-driven paddle changes their paths, and three sensor gates count the first arrival of each marble.",
    actions:[{id:"run-sorter",label:"Run a fresh batch",primary:true,restart:true,play:true,request:{operation:"read"}}],
    labels:["BLUE GATE","GREEN GATE","AMBER GATE"],
    note:"The paddle is kinematic: it keeps turning when marbles hit it. The coloured gates sense overlap without pushing marbles, and each marble is counted once."
})
